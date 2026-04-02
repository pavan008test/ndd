pipeline {
    agent {
        docker {
            image 'x86_sonar:latest'
        }
    }
    
     environment{
            STATUS_MESSAGE=" "
            STATUS=" "
        }

    stages{
        
        stage('Set Build status in PR'){
            steps {
                script {
                    gitHubPRStatus githubPRMessage('Compilation run started')
                }
            }   
        }

        stage('Get Dependency'){
            steps {
                script {
                    withCredentials([string(credentialsId: 'device-alfred_github', variable: 'SECRET')]) {
                        def ndcorebranch = sh (returnStdout: true, script: '''
                            if [ -z ${CHANGE_ID} ]; then
                                responsebr=$(curl -s -H "Authorization: token ${SECRET}" "https://api.github.com/repos/netradyne/nd_core_utils/branches/${BRANCH_NAME}")
                                if [[ $(echo "$responsebr" | jq '.name' | sed -e 's/^"//' -e 's/"$//') != "null" ]]; then
                                    export Nd_Core_Utils_Branch=${BRANCH_NAME}
                                else
                                    export Nd_Core_Utils_Branch="release"
                                fi
                            else
                                responsepr=$(curl -s -H "Authorization: token ${SECRET}" "https://api.github.com/repos/netradyne/nd_device_services/pulls/${CHANGE_ID}")
                                responsehd=$(curl -s -H "Authorization: token ${SECRET}" "https://api.github.com/repos/netradyne/nd_core_utils/branches/$(echo "$responsepr" | jq .head.ref | sed -e 's/^"//' -e 's/"$//')")
                                responsebs=$(curl -s -H "Authorization: token ${SECRET}" "https://api.github.com/repos/netradyne/nd_core_utils/branches/$(echo "$responsepr" | jq .base.ref | sed -e 's/^"//' -e 's/"$//')")
                                if echo "$responsepr" | jq '.body' | grep -q '\\[x\\] Nd_core_utils dependency'; then
                                    nd_core_utils_branch=$(echo "$responsepr" | jq -r .body | grep -i 'nd_core_utils_branch' | cut -d ':' -f2 | awk '{print$NF}'| tr -d '\r')
                                    export Nd_Core_Utils_Branch=${nd_core_utils_branch}
                                else
                                    if [[ $(echo "$responsehd" | jq '.name' | sed -e 's/^"//' -e 's/"$//') != "null" ]]; then
                                        nd_core_utils_branch=$(echo "$responsepr" | jq .head.ref | sed -e 's/^"//' -e 's/"$//')
                                        export Nd_Core_Utils_Branch=${nd_core_utils_branch}
                                    elif [[ $(echo "$responsebs" | jq '.name' | sed -e 's/^"//' -e 's/"$//') != "null" ]]; then
                                        nd_core_utils_branch=$(echo "$responsepr" | jq .base.ref | sed -e 's/^"//' -e 's/"$//')
                                        export Nd_Core_Utils_Branch=${nd_core_utils_branch}
                                    else
                                        export Nd_Core_Utils_Branch="release"
                                    fi
                                fi
                            fi
                            echo "${Nd_Core_Utils_Branch}"
                            
                        ''' ).trim()
                        env.Nd_Core_Utils_Branch = ndcorebranch
                        echo "${env.Nd_Core_Utils_Branch}"
                    }
                }
            }   
        }
                
        stage('Build Sonar Wrapper'){
            steps {

                dir ('nd_device_services') {
                checkout scm
		        }
		        checkout poll: false, scm: [$class: 'GitSCM', 
                    branches: [[name: "${env.Nd_Core_Utils_Branch}"]], 
                    extensions: [[$class: 'RelativeTargetDirectory', relativeTargetDir: 'nd_core_utils'], [$class: 'CleanBeforeCheckout'], [$class: 'CloneOption', noTags: false, reference: '', shallow: true], [$class: 'SubmoduleOption', disableSubmodules: false, parentCredentials: false, recursiveSubmodules: true, reference: '', trackingSubmodules: false]], 
                    userRemoteConfigs: [[url: 'git@github.com:netradyne/nd_core_utils.git']]] 

                script {
                    STATUS = sh (returnStatus: true, script: '''
                        echo "===========Nd_core_compilation=========="
                        cd $WORKSPACE/nd_core_utils/cpp/ ; make x86 ;
                        echo "===========nd_device_Services compilation=========="     
                        cd $WORKSPACE/nd_device_services
                        export ND_CORE_LIB_ROOT=$WORKSPACE/nd_core_utils
                        build-wrapper-linux-x86-64 --out-dir ./ make x86

                    ''' )
                }

            }

            post{
                always{
                    script{
                        try {
                            echo "BUILD_STATUS is ${STATUS}"
                            if ( "${STATUS}" == "0" ) {
                                echo "Build is success"
                                STATUS_MESSAGE = "Build Success" 
                            }
                            else {
                                echo "Build Failed"
                                STATUS_MESSAGE = "Build Failed"
                                sh "exit 1"
                            }
                        }
                        catch (Exception e) {
                            echo 'Failed - Exception occurred: ' + e.toString()
                            sh "exit 1"
                        }

                    }

                }

            }
                
        }

        stage("SonarQube Analysis"){
            steps{
                script {
                    
                    def scannerHome = tool 'SonarQubeScanner';
                    withSonarQubeEnv() {
                        sh "cd $WORKSPACE/nd_device_services ; ${scannerHome}/bin/sonar-scanner"
                    }
                    //def qualitygate = waitForQualityGate()
                    //if (qualitygate.status != "OK") {
                      //  error "Pipeline aborted due to quality gate coverage failure: ${qualitygate.status}"
                        //STATUS_MESSAGE = "SonarCode Analysis Done - Failed"
                    //}
                    //else {
                    //    STATUS_MESSAGE = "SonarCode Analysis Done - Passed"
                     //   echo $STATUS_MESSAGE
                    //}

                }
            }
            
        }
    }
    post {
        always {
            cleanWs(cleanWhenNotBuilt: false,
                    deleteDirs: true,
                    notFailBuild: true)
        }
    }
}
